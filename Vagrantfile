Vagrant.configure("2") do |c|
  c.vm.box = "opscode-centos-6.5"
  c.vm.box_url = "https://opscode-vm-bento.s3.amazonaws.com/vagrant/virtualbox/opscode_centos-6.5_chef-provisionerless.box"
  c.vm.hostname = "default-centos-65.vagrantup.com"
  c.vm.network(:forwarded_port, {:guest=>8000, :host=>8080})
  c.vm.synced_folder ".", "/home/vagrant/project"
  c.vm.provider :virtualbox do |p|
  end
end
